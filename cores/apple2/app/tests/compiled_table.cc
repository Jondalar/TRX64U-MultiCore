// The application's real core table under another name, for tests that need the compiled-in descriptors next to a table of their own (test_descgen: the record's core is
// compared with the compiled-in descriptor of the same core, while the start must not find that one).
// the Apple IIe's app is its own build (run.sh apple2app): its table is its core only
#include "cores.h"
extern const CoreDesc apple2_desc;   // cores.h lists the shared app's cores (cores/apple2/app/lib holds this one)
extern const CoreDesc *const compiled_in_table[];
const CoreDesc *const compiled_in_table[] = { &apple2_desc, 0 };
