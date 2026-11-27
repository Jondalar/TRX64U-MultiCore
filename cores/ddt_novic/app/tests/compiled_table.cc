// The application's real core table under another name, for tests that need the compiled-in descriptors next to a table of their own (test_descgen: the record's core is
// compared with the compiled-in descriptor of the same core, while the start must not find that one).
// DDT Look Ma, No VIC's app is its own build (run.sh ddtapp): its table is its core only
#include "cores.h"
extern const CoreDesc ddt_novic_desc;   // cores.h lists the shared app's cores (cores/ddt_novic/app/lib holds this one)
extern const CoreDesc *const compiled_in_table[];
const CoreDesc *const compiled_in_table[] = { &ddt_novic_desc, 0 };
